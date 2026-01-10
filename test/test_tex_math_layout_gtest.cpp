// test_tex_math_layout_gtest.cpp - Unit tests for TeX math layout algorithms

#include <gtest/gtest.h>
#include "../lambda/tex/tex_math_layout.hpp"
#include "../lambda/tex/tex_box.hpp"
#include "../lambda/tex/tex_font_metrics.hpp"
#include "../lib/arena.h"

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class MathLayoutTest : public ::testing::Test {
protected:
    Arena arena;
    MathLayoutContext ctx;

    void SetUp() override {
        arena_init(&arena, 64 * 1024);

        // Initialize layout context with test metrics
        ctx.arena = &arena;
        ctx.style = MathStyle::Display;
        ctx.font_size = 10.0f;  // 10pt base
        ctx.font_metrics = get_default_math_font_metrics();
    }

    void TearDown() override {
        arena_destroy(&arena);
    }

    // Helper to create a simple atom
    MathAtom* make_ord_atom(int codepoint) {
        MathAtom* atom = (MathAtom*)arena_alloc(&arena, sizeof(MathAtom));
        atom->type = AtomType::Ord;
        atom->nucleus.type = MathFieldType::Symbol;
        atom->nucleus.symbol.codepoint = codepoint;
        atom->nucleus.symbol.family = 0;
        atom->superscript = nullptr;
        atom->subscript = nullptr;
        atom->next = nullptr;
        return atom;
    }

    MathAtom* make_op_atom(const char* name) {
        MathAtom* atom = (MathAtom*)arena_alloc(&arena, sizeof(MathAtom));
        atom->type = AtomType::Op;
        atom->nucleus.type = MathFieldType::Symbol;
        atom->nucleus.symbol.codepoint = 0;  // Will be looked up
        atom->op.name = name;
        atom->op.has_limits = true;
        atom->superscript = nullptr;
        atom->subscript = nullptr;
        atom->next = nullptr;
        return atom;
    }

    MathAtom* make_bin_atom(int codepoint) {
        MathAtom* atom = (MathAtom*)arena_alloc(&arena, sizeof(MathAtom));
        atom->type = AtomType::Bin;
        atom->nucleus.type = MathFieldType::Symbol;
        atom->nucleus.symbol.codepoint = codepoint;
        atom->superscript = nullptr;
        atom->subscript = nullptr;
        atom->next = nullptr;
        return atom;
    }

    MathAtom* make_rel_atom(int codepoint) {
        MathAtom* atom = (MathAtom*)arena_alloc(&arena, sizeof(MathAtom));
        atom->type = AtomType::Rel;
        atom->nucleus.type = MathFieldType::Symbol;
        atom->nucleus.symbol.codepoint = codepoint;
        atom->superscript = nullptr;
        atom->subscript = nullptr;
        atom->next = nullptr;
        return atom;
    }
};

// ============================================================================
// Style Tests
// ============================================================================

TEST_F(MathLayoutTest, StyleProgression) {
    // Display -> Text -> Script -> ScriptScript
    EXPECT_EQ(cramped_style(MathStyle::Display), MathStyle::DisplayCramped);
    EXPECT_EQ(cramped_style(MathStyle::Text), MathStyle::TextCramped);

    EXPECT_EQ(superscript_style(MathStyle::Display), MathStyle::Script);
    EXPECT_EQ(superscript_style(MathStyle::Text), MathStyle::Script);
    EXPECT_EQ(superscript_style(MathStyle::Script), MathStyle::ScriptScript);

    EXPECT_EQ(subscript_style(MathStyle::Display), MathStyle::ScriptCramped);
    EXPECT_EQ(subscript_style(MathStyle::Text), MathStyle::ScriptCramped);
}

TEST_F(MathLayoutTest, FractionStyles) {
    EXPECT_EQ(numerator_style(MathStyle::Display), MathStyle::Text);
    EXPECT_EQ(denominator_style(MathStyle::Display), MathStyle::TextCramped);

    EXPECT_EQ(numerator_style(MathStyle::Text), MathStyle::Script);
    EXPECT_EQ(denominator_style(MathStyle::Text), MathStyle::ScriptCramped);
}

TEST_F(MathLayoutTest, StyleFontSize) {
    // Font size decreases with style
    float display_size = font_size_for_style(MathStyle::Display, 10.0f);
    float text_size = font_size_for_style(MathStyle::Text, 10.0f);
    float script_size = font_size_for_style(MathStyle::Script, 10.0f);
    float scriptscript_size = font_size_for_style(MathStyle::ScriptScript, 10.0f);

    EXPECT_FLOAT_EQ(display_size, 10.0f);
    EXPECT_FLOAT_EQ(text_size, 10.0f);
    EXPECT_LT(script_size, text_size);
    EXPECT_LT(scriptscript_size, script_size);
}

// ============================================================================
// Atom Spacing Tests
// ============================================================================

TEST_F(MathLayoutTest, OrdOrdSpacing) {
    // Ord + Ord = no space
    float space = inter_atom_space(AtomType::Ord, AtomType::Ord, MathStyle::Display);
    EXPECT_FLOAT_EQ(space, 0.0f);
}

TEST_F(MathLayoutTest, OrdBinSpacing) {
    // Ord + Bin = medium space
    float space = inter_atom_space(AtomType::Ord, AtomType::Bin, MathStyle::Display);
    EXPECT_GT(space, 0.0f);
}

TEST_F(MathLayoutTest, BinOrdSpacing) {
    // Bin + Ord = medium space
    float space = inter_atom_space(AtomType::Bin, AtomType::Ord, MathStyle::Display);
    EXPECT_GT(space, 0.0f);
}

TEST_F(MathLayoutTest, OrdRelSpacing) {
    // Ord + Rel = thick space
    float space = inter_atom_space(AtomType::Ord, AtomType::Rel, MathStyle::Display);
    EXPECT_GT(space, inter_atom_space(AtomType::Ord, AtomType::Bin, MathStyle::Display));
}

TEST_F(MathLayoutTest, SpacingInScript) {
    // Spacing is reduced in script style
    float display_space = inter_atom_space(AtomType::Ord, AtomType::Bin, MathStyle::Display);
    float script_space = inter_atom_space(AtomType::Ord, AtomType::Bin, MathStyle::Script);

    EXPECT_LE(script_space, display_space);
}

// ============================================================================
// Single Atom Layout Tests
// ============================================================================

TEST_F(MathLayoutTest, OrdAtomLayout) {
    MathAtom* atom = make_ord_atom('x');

    TexBox* box = layout_math_atom(atom, &ctx);

    ASSERT_NE(box, nullptr);
    EXPECT_GT(box->width, 0.0f);
    EXPECT_GT(box->height, 0.0f);
}

TEST_F(MathLayoutTest, OperatorAtomLayout) {
    MathAtom* atom = make_op_atom("sum");

    ctx.style = MathStyle::Display;  // Display style for large operator
    TexBox* box = layout_math_atom(atom, &ctx);

    ASSERT_NE(box, nullptr);
    // Sum should be larger in display mode
    EXPECT_GT(box->height + box->depth, 10.0f);
}

TEST_F(MathLayoutTest, BinaryOperatorLayout) {
    MathAtom* atom = make_bin_atom('+');

    TexBox* box = layout_math_atom(atom, &ctx);

    ASSERT_NE(box, nullptr);
    EXPECT_GT(box->width, 0.0f);
}

// ============================================================================
// Superscript/Subscript Layout Tests
// ============================================================================

TEST_F(MathLayoutTest, SuperscriptLayout) {
    MathAtom* base = make_ord_atom('x');
    MathAtom* super = make_ord_atom('2');
    base->superscript = super;

    TexBox* box = layout_math_atom(base, &ctx);

    ASSERT_NE(box, nullptr);
    // With superscript, box should be taller
    EXPECT_GT(box->height, 5.0f);
}

TEST_F(MathLayoutTest, SubscriptLayout) {
    MathAtom* base = make_ord_atom('x');
    MathAtom* sub = make_ord_atom('i');
    base->subscript = sub;

    TexBox* box = layout_math_atom(base, &ctx);

    ASSERT_NE(box, nullptr);
    // With subscript, box should have more depth
    EXPECT_GT(box->depth, 0.0f);
}

TEST_F(MathLayoutTest, SubSuperscriptLayout) {
    MathAtom* base = make_ord_atom('x');
    MathAtom* super = make_ord_atom('2');
    MathAtom* sub = make_ord_atom('i');
    base->superscript = super;
    base->subscript = sub;

    TexBox* box = layout_math_atom(base, &ctx);

    ASSERT_NE(box, nullptr);
    // Should have both height and depth
    EXPECT_GT(box->height, 5.0f);
    EXPECT_GT(box->depth, 0.0f);
}

// ============================================================================
// Operator Limits Tests
// ============================================================================

TEST_F(MathLayoutTest, OperatorWithLimits) {
    MathAtom* op = make_op_atom("sum");
    MathAtom* lower = make_ord_atom('i');
    MathAtom* upper = make_ord_atom('n');
    op->subscript = lower;
    op->superscript = upper;

    ctx.style = MathStyle::Display;
    TexBox* box = layout_math_atom(op, &ctx);

    ASSERT_NE(box, nullptr);
    // Limits should make the operator taller
    EXPECT_GT(box->height + box->depth, 20.0f);
}

// ============================================================================
// Fraction Layout Tests
// ============================================================================

TEST_F(MathLayoutTest, SimpleFractionLayout) {
    MathAtom* num = make_ord_atom('a');
    MathAtom* denom = make_ord_atom('b');

    TexBox* box = layout_fraction(num, denom, FractionStyle::Normal, &ctx);

    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->kind, BoxKind::Fraction);
    EXPECT_GT(box->height + box->depth, 15.0f);
}

TEST_F(MathLayoutTest, FractionRuleThickness) {
    MathAtom* num = make_ord_atom('a');
    MathAtom* denom = make_ord_atom('b');

    TexBox* box = layout_fraction(num, denom, FractionStyle::Normal, &ctx);

    ASSERT_NE(box, nullptr);
    EXPECT_GT(box->content.fraction.rule_thickness, 0.0f);
}

TEST_F(MathLayoutTest, BinomialLayout) {
    MathAtom* n = make_ord_atom('n');
    MathAtom* k = make_ord_atom('k');

    TexBox* box = layout_fraction(n, k, FractionStyle::Binomial, &ctx);

    ASSERT_NE(box, nullptr);
    // Binomial has no rule
    EXPECT_FLOAT_EQ(box->content.fraction.rule_thickness, 0.0f);
}

TEST_F(MathLayoutTest, NestedFraction) {
    // (a/b) / c
    MathAtom* a = make_ord_atom('a');
    MathAtom* b = make_ord_atom('b');
    MathAtom* c = make_ord_atom('c');

    TexBox* inner = layout_fraction(a, b, FractionStyle::Normal, &ctx);

    // Layout outer fraction with inner as numerator
    MathAtom wrapper;
    wrapper.type = AtomType::Ord;
    wrapper.nucleus.type = MathFieldType::Box;
    wrapper.nucleus.box = inner;

    TexBox* outer = layout_fraction(&wrapper, c, FractionStyle::Normal, &ctx);

    ASSERT_NE(outer, nullptr);
    // Nested fraction should be taller
    EXPECT_GT(outer->height + outer->depth, inner->height + inner->depth);
}

// ============================================================================
// Radical Layout Tests
// ============================================================================

TEST_F(MathLayoutTest, SimpleRadicalLayout) {
    MathAtom* radicand = make_ord_atom('x');

    TexBox* box = layout_radical(radicand, nullptr, &ctx);

    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->kind, BoxKind::Radical);
    EXPECT_GT(box->height, 5.0f);
}

TEST_F(MathLayoutTest, RadicalWithIndex) {
    MathAtom* radicand = make_ord_atom('x');
    MathAtom* index = make_ord_atom('3');

    TexBox* box = layout_radical(radicand, index, &ctx);

    ASSERT_NE(box, nullptr);
    // Radical with index should be wider
}

TEST_F(MathLayoutTest, RadicalCoversRadicand) {
    MathAtom* radicand = make_ord_atom('x');

    TexBox* box = layout_radical(radicand, nullptr, &ctx);

    ASSERT_NE(box, nullptr);
    // Radical sign should add clearance above radicand
    EXPECT_GT(box->height, ctx.font_size * 0.7f);
}

// ============================================================================
// Delimiter Layout Tests
// ============================================================================

TEST_F(MathLayoutTest, SimpleDelimiterLayout) {
    TexBox* box = layout_delimiter('(', true, 10.0f, &ctx);

    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->kind, BoxKind::Delimiter);
}

TEST_F(MathLayoutTest, DelimiterScaling) {
    TexBox* small = layout_delimiter('(', true, 10.0f, &ctx);
    TexBox* large = layout_delimiter('(', true, 30.0f, &ctx);

    ASSERT_NE(small, nullptr);
    ASSERT_NE(large, nullptr);

    EXPECT_GT(large->height + large->depth, small->height + small->depth);
}

TEST_F(MathLayoutTest, MatchingDelimiters) {
    // Content that determines delimiter size
    MathAtom* content = make_ord_atom('x');
    TexBox* content_box = layout_math_atom(content, &ctx);
    float target_height = content_box->height + content_box->depth;

    TexBox* left = layout_delimiter('(', true, target_height, &ctx);
    TexBox* right = layout_delimiter(')', false, target_height, &ctx);

    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);

    // Both should have similar height
    EXPECT_NEAR(left->height + left->depth, right->height + right->depth, 1.0f);
}

// ============================================================================
// Math List Layout Tests
// ============================================================================

TEST_F(MathLayoutTest, MathListLayout) {
    // a + b = c
    MathAtom* a = make_ord_atom('a');
    MathAtom* plus = make_bin_atom('+');
    MathAtom* b = make_ord_atom('b');
    MathAtom* eq = make_rel_atom('=');
    MathAtom* c = make_ord_atom('c');

    a->next = plus;
    plus->next = b;
    b->next = eq;
    eq->next = c;

    TexBox* box = layout_math_list(a, &ctx);

    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->kind, BoxKind::HBox);
    // Should have spacing between elements
    EXPECT_GT(box->width, 20.0f);
}

TEST_F(MathLayoutTest, EmptyMathList) {
    TexBox* box = layout_math_list(nullptr, &ctx);

    // Empty list should produce empty box
    ASSERT_NE(box, nullptr);
    EXPECT_FLOAT_EQ(box->width, 0.0f);
}

// ============================================================================
// Accent Layout Tests
// ============================================================================

TEST_F(MathLayoutTest, AccentLayout) {
    MathAtom* base = make_ord_atom('x');

    TexBox* box = layout_accent(base, 0x0302, &ctx);  // circumflex ^

    ASSERT_NE(box, nullptr);
    // Accent should add some height
    EXPECT_GT(box->height, ctx.font_size * 0.7f);
}

// ============================================================================
// Font Metrics Tests
// ============================================================================

TEST_F(MathLayoutTest, FontMetricsAvailable) {
    ASSERT_NE(ctx.font_metrics, nullptr);
}

TEST_F(MathLayoutTest, AxisHeight) {
    float axis = ctx.font_metrics->axis_height * ctx.font_size;
    EXPECT_GT(axis, 0.0f);
    EXPECT_LT(axis, ctx.font_size);  // Should be less than full font size
}

TEST_F(MathLayoutTest, FractionRuleFromMetrics) {
    float rule = ctx.font_metrics->fraction_rule_thickness * ctx.font_size;
    EXPECT_GT(rule, 0.0f);
    EXPECT_LT(rule, 1.0f);  // Should be thin
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
