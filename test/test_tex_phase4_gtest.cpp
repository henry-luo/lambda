// test_tex_phase4_gtest.cpp - Phase 4: Math Integration Tests
//
// Tests for math bridge functionality:
// - Simple math string typesetting
// - Fractions, radicals, scripts
// - Inline math ($...$) extraction
// - Display math ($$...$$)

#include <gtest/gtest.h>
#include "lambda/tex/tex_math_bridge.hpp"
#include "lambda/tex/tex_node.hpp"
#include "lambda/tex/tex_tfm.hpp"
#include "lambda/tex/tex_hlist.hpp"
#include "lambda/tex/tex_vlist.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include <cstring>
#include <cmath>

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class MathBridgeTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    TFMFontManager* fonts;

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
        fonts = create_font_manager(arena);
    }

    void TearDown() override {
        arena_destroy(arena);
        pool_destroy(pool);
    }

    MathContext create_context(float size = 10.0f) {
        return MathContext::create(arena, fonts, size);
    }
};

// ============================================================================
// Math Style Tests
// ============================================================================

TEST_F(MathBridgeTest, StyleSizeFactor) {
    // Display and text styles are full size
    EXPECT_FLOAT_EQ(1.0f, style_size_factor(MathStyle::Display));
    EXPECT_FLOAT_EQ(1.0f, style_size_factor(MathStyle::Text));
    EXPECT_FLOAT_EQ(1.0f, style_size_factor(MathStyle::DisplayPrime));
    EXPECT_FLOAT_EQ(1.0f, style_size_factor(MathStyle::TextPrime));

    // Script styles are 70%
    EXPECT_FLOAT_EQ(0.7f, style_size_factor(MathStyle::Script));
    EXPECT_FLOAT_EQ(0.7f, style_size_factor(MathStyle::ScriptPrime));

    // ScriptScript styles are 50%
    EXPECT_FLOAT_EQ(0.5f, style_size_factor(MathStyle::ScriptScript));
    EXPECT_FLOAT_EQ(0.5f, style_size_factor(MathStyle::ScriptScriptPrime));
}

TEST_F(MathBridgeTest, StyleTransitions) {
    // script_style transitions
    EXPECT_EQ(MathStyle::Script, sup_style(MathStyle::Display));
    EXPECT_EQ(MathStyle::Script, sup_style(MathStyle::Text));
    EXPECT_EQ(MathStyle::ScriptPrime, sup_style(MathStyle::DisplayPrime));
    EXPECT_EQ(MathStyle::ScriptScript, sup_style(MathStyle::Script));
    EXPECT_EQ(MathStyle::ScriptScriptPrime, sup_style(MathStyle::ScriptPrime));

    // cramped_style transitions
    EXPECT_EQ(MathStyle::DisplayPrime, cramped_style(MathStyle::Display));
    EXPECT_EQ(MathStyle::TextPrime, cramped_style(MathStyle::Text));
    EXPECT_EQ(MathStyle::ScriptPrime, cramped_style(MathStyle::Script));

    // is_cramped checks
    EXPECT_FALSE(is_cramped(MathStyle::Display));
    EXPECT_TRUE(is_cramped(MathStyle::DisplayPrime));
    EXPECT_FALSE(is_cramped(MathStyle::Text));
    EXPECT_TRUE(is_cramped(MathStyle::TextPrime));
}

// ============================================================================
// Atom Classification Tests
// ============================================================================

TEST_F(MathBridgeTest, ClassifyCodepoints) {
    // Binary operators
    EXPECT_EQ(AtomType::Bin, classify_codepoint('+'));
    EXPECT_EQ(AtomType::Bin, classify_codepoint('-'));
    EXPECT_EQ(AtomType::Bin, classify_codepoint('*'));

    // Relations
    EXPECT_EQ(AtomType::Rel, classify_codepoint('='));
    EXPECT_EQ(AtomType::Rel, classify_codepoint('<'));
    EXPECT_EQ(AtomType::Rel, classify_codepoint('>'));

    // Delimiters
    EXPECT_EQ(AtomType::Open, classify_codepoint('('));
    EXPECT_EQ(AtomType::Open, classify_codepoint('['));
    EXPECT_EQ(AtomType::Open, classify_codepoint('{'));
    EXPECT_EQ(AtomType::Close, classify_codepoint(')'));
    EXPECT_EQ(AtomType::Close, classify_codepoint(']'));
    EXPECT_EQ(AtomType::Close, classify_codepoint('}'));

    // Punctuation
    EXPECT_EQ(AtomType::Punct, classify_codepoint(','));
    EXPECT_EQ(AtomType::Punct, classify_codepoint(';'));

    // Ordinary
    EXPECT_EQ(AtomType::Ord, classify_codepoint('a'));
    EXPECT_EQ(AtomType::Ord, classify_codepoint('x'));
    EXPECT_EQ(AtomType::Ord, classify_codepoint('0'));
    EXPECT_EQ(AtomType::Ord, classify_codepoint('9'));
}

// ============================================================================
// Inter-Atom Spacing Tests
// ============================================================================

TEST_F(MathBridgeTest, AtomSpacing) {
    // In text style
    MathStyle text = MathStyle::Text;

    // No space between Ord and Ord
    EXPECT_FLOAT_EQ(0.0f, get_atom_spacing_mu(AtomType::Ord, AtomType::Ord, text));

    // Thin space (3mu) between Ord and Op
    EXPECT_FLOAT_EQ(3.0f, get_atom_spacing_mu(AtomType::Ord, AtomType::Op, text));

    // Medium space (4mu) between Ord and Bin
    EXPECT_FLOAT_EQ(4.0f, get_atom_spacing_mu(AtomType::Ord, AtomType::Bin, text));

    // Thick space (5mu) between Ord and Rel
    EXPECT_FLOAT_EQ(5.0f, get_atom_spacing_mu(AtomType::Ord, AtomType::Rel, text));

    // No space before Open from Ord
    EXPECT_FLOAT_EQ(0.0f, get_atom_spacing_mu(AtomType::Ord, AtomType::Open, text));
    // Bin before Open gets medium space (4mu) per TeXBook
    EXPECT_FLOAT_EQ(4.0f, get_atom_spacing_mu(AtomType::Bin, AtomType::Open, text));

    // Script style has reduced spacing
    MathStyle script = MathStyle::Script;
    EXPECT_FLOAT_EQ(0.0f, get_atom_spacing_mu(AtomType::Ord, AtomType::Bin, script));
    EXPECT_FLOAT_EQ(0.0f, get_atom_spacing_mu(AtomType::Ord, AtomType::Rel, script));
}

TEST_F(MathBridgeTest, MuToPoint) {
    auto ctx = create_context(10.0f);

    // 1 mu = 1/18 quad
    // At 10pt, quad = 10pt, so 1mu = 10/18 pt ≈ 0.556pt
    float mu1 = mu_to_pt(1.0f, ctx);
    EXPECT_NEAR(10.0f / 18.0f, mu1, 0.01f);

    // 18 mu = 1 quad = 10pt
    float mu18 = mu_to_pt(18.0f, ctx);
    EXPECT_NEAR(10.0f, mu18, 0.01f);
}

// ============================================================================
// Simple Math String Tests
// ============================================================================

TEST_F(MathBridgeTest, TypesetSimpleExpression) {
    auto ctx = create_context(10.0f);

    TexNode* result = typeset_latex_math("a+b", 3, ctx);

    ASSERT_NE(nullptr, result);
    EXPECT_EQ(NodeClass::HBox, result->node_class);
    EXPECT_GT(result->width, 0);
    EXPECT_GT(result->height, 0);
}

TEST_F(MathBridgeTest, TypesetWithSpacing) {
    auto ctx = create_context(10.0f);

    // "a + b" should have spacing around +
    TexNode* spaced = typeset_latex_math("a + b", 5, ctx);
    // "a+b" without explicit spaces should also get automatic spacing
    TexNode* unspaced = typeset_latex_math("a+b", 3, ctx);

    ASSERT_NE(nullptr, spaced);
    ASSERT_NE(nullptr, unspaced);

    // Both should have similar widths due to automatic math spacing
    EXPECT_NEAR(spaced->width, unspaced->width, 1.0f);
}

TEST_F(MathBridgeTest, TypesetEquation) {
    auto ctx = create_context(10.0f);

    TexNode* result = typeset_latex_math("x = y + z", 9, ctx);

    ASSERT_NE(nullptr, result);
    EXPECT_GT(result->width, 0);

    // Count children (should have chars + spacing kerns)
    int count = 0;
    for (TexNode* n = result->first_child; n; n = n->next_sibling) {
        count++;
    }
    EXPECT_GE(count, 5);  // At least x, =, y, +, z
}

TEST_F(MathBridgeTest, TypesetDigits) {
    auto ctx = create_context(10.0f);

    TexNode* result = typeset_latex_math("123", 3, ctx);

    ASSERT_NE(nullptr, result);

    // Digits should use roman font
    TexNode* first = result->first_child;
    ASSERT_NE(nullptr, first);
    EXPECT_EQ(NodeClass::MathChar, first->node_class);
}

TEST_F(MathBridgeTest, TypesetEmpty) {
    auto ctx = create_context(10.0f);

    TexNode* result = typeset_latex_math("", 0, ctx);

    ASSERT_NE(nullptr, result);
    EXPECT_EQ(NodeClass::HBox, result->node_class);
}

// ============================================================================
// Fraction Tests
// ============================================================================

TEST_F(MathBridgeTest, TypesetSimpleFraction) {
    auto ctx = create_context(10.0f);

    TexNode* result = typeset_fraction_strings("1", "2", ctx);

    ASSERT_NE(nullptr, result);
    EXPECT_GT(result->width, 0);
    EXPECT_GT(result->height, 0);
    EXPECT_GT(result->depth, 0);

    // Fraction should have numerator, bar, denominator
    EXPECT_NE(nullptr, result->content.frac.numerator);
    EXPECT_NE(nullptr, result->content.frac.denominator);
}

TEST_F(MathBridgeTest, FractionWithExpression) {
    auto ctx = create_context(10.0f);

    TexNode* result = typeset_fraction_strings("a+b", "c-d", ctx);

    ASSERT_NE(nullptr, result);

    // Numerator and denominator should be HBoxes with content
    TexNode* num = result->content.frac.numerator;
    TexNode* denom = result->content.frac.denominator;

    ASSERT_NE(nullptr, num);
    ASSERT_NE(nullptr, denom);
    EXPECT_GT(num->width, 0);
    EXPECT_GT(denom->width, 0);
}

TEST_F(MathBridgeTest, FractionCentering) {
    auto ctx = create_context(10.0f);

    // Create fraction with numerator wider than denominator
    TexNode* num = typeset_latex_math("abcdef", 6, ctx);
    TexNode* denom = typeset_latex_math("x", 1, ctx);

    TexNode* result = typeset_fraction(num, denom, ctx.rule_thickness, ctx);

    // Denominator should be centered
    float num_center = num->x + num->width / 2.0f;
    float denom_center = denom->x + denom->width / 2.0f;
    EXPECT_NEAR(num_center, denom_center, 1.0f);
}

// ============================================================================
// Square Root Tests
// ============================================================================

TEST_F(MathBridgeTest, TypesetSqrt) {
    auto ctx = create_context(10.0f);

    TexNode* result = typeset_sqrt_string("x", ctx);

    ASSERT_NE(nullptr, result);
    EXPECT_EQ(NodeClass::Radical, result->node_class);
    EXPECT_GT(result->width, 0);
    EXPECT_GT(result->height, 0);

    // Should have radicand
    EXPECT_NE(nullptr, result->content.radical.radicand);
}

TEST_F(MathBridgeTest, SqrtWithExpression) {
    auto ctx = create_context(10.0f);

    TexNode* result = typeset_sqrt_string("a+b", ctx);

    ASSERT_NE(nullptr, result);
    EXPECT_GT(result->width, 0);

    TexNode* radicand = result->content.radical.radicand;
    ASSERT_NE(nullptr, radicand);
    EXPECT_GT(radicand->width, 0);
}

TEST_F(MathBridgeTest, SqrtClearance) {
    auto ctx = create_context(10.0f);

    TexNode* radicand = typeset_latex_math("x", 1, ctx);
    TexNode* result = typeset_sqrt(radicand, ctx);

    // Sqrt should have clearance above radicand
    float rule_thickness = result->content.radical.rule_thickness;
    float rule_y = result->content.radical.rule_y;

    EXPECT_GT(rule_y, radicand->height);
    EXPECT_GT(rule_thickness, 0);
}

// ============================================================================
// Subscript/Superscript Tests
// ============================================================================

TEST_F(MathBridgeTest, Superscript) {
    auto ctx = create_context(10.0f);

    TexNode* base = typeset_latex_math("x", 1, ctx);
    TexNode* sup = typeset_latex_math("2", 1, ctx);

    TexNode* result = typeset_scripts(base, nullptr, sup, ctx);

    ASSERT_NE(nullptr, result);
    EXPECT_EQ(NodeClass::Scripts, result->node_class);

    // Superscript should be raised
    EXPECT_GT(sup->y, 0);

    // Total height should include raised superscript
    EXPECT_GT(result->height, base->height);
}

TEST_F(MathBridgeTest, Subscript) {
    auto ctx = create_context(10.0f);

    TexNode* base = typeset_latex_math("x", 1, ctx);
    TexNode* sub = typeset_latex_math("i", 1, ctx);

    TexNode* result = typeset_scripts(base, sub, nullptr, ctx);

    ASSERT_NE(nullptr, result);

    // Subscript should be lowered
    EXPECT_LT(sub->y, 0);

    // Total depth should include lowered subscript
    EXPECT_GT(result->depth, base->depth);
}

TEST_F(MathBridgeTest, BothScripts) {
    auto ctx = create_context(10.0f);

    TexNode* base = typeset_latex_math("x", 1, ctx);
    TexNode* sub = typeset_latex_math("i", 1, ctx);
    TexNode* sup = typeset_latex_math("2", 1, ctx);

    TexNode* result = typeset_scripts(base, sub, sup, ctx);

    ASSERT_NE(nullptr, result);
    EXPECT_EQ(NodeClass::Scripts, result->node_class);

    // Both scripts should be positioned
    EXPECT_GT(sup->y, 0);
    EXPECT_LT(sub->y, 0);
}

TEST_F(MathBridgeTest, ItalicCorrection) {
    auto ctx = create_context(10.0f);

    // Italic 'f' should have italic correction
    TexNode* base = typeset_math_string("f", 1, ctx);
    TexNode* sup = typeset_math_string("2", 1, ctx);

    TexNode* result = typeset_scripts(base, nullptr, sup, ctx);

    // Superscript should be shifted right by italic correction
    // (amount depends on font metrics)
    EXPECT_GE(sup->x, base->width);
}

// ============================================================================
// Delimiter Tests
// ============================================================================

TEST_F(MathBridgeTest, Parentheses) {
    auto ctx = create_context(10.0f);

    TexNode* content = typeset_math_string("x+y", 3, ctx);
    TexNode* result = typeset_delimited('(', content, ')', ctx);

    ASSERT_NE(nullptr, result);

    // Should be wider than content alone (delimiters added)
    EXPECT_GT(result->width, content->width);
}

TEST_F(MathBridgeTest, Brackets) {
    auto ctx = create_context(10.0f);

    TexNode* content = typeset_math_string("a", 1, ctx);
    TexNode* result = typeset_delimited('[', content, ']', ctx);

    ASSERT_NE(nullptr, result);
    EXPECT_GT(result->width, content->width);
}

TEST_F(MathBridgeTest, LeftDelimiterOnly) {
    auto ctx = create_context(10.0f);

    TexNode* content = typeset_math_string("x", 1, ctx);
    TexNode* result = typeset_delimited('(', content, 0, ctx);

    ASSERT_NE(nullptr, result);
    // Should have left delimiter but not right
}

// ============================================================================
// Math Region Detection Tests
// ============================================================================

TEST_F(MathBridgeTest, FindInlineMath) {
    const char* text = "The value is $x + y$ here.";
    MathRegionList regions = find_math_regions(text, strlen(text), arena);

    EXPECT_EQ(1, regions.count);
    if (regions.count >= 1) {
        EXPECT_FALSE(regions.regions[0].is_display);
        EXPECT_EQ(5, (int)regions.regions[0].content_len);  // "x + y" = 5 chars
    }
}

TEST_F(MathBridgeTest, FindDisplayMath) {
    const char* text = "The equation $$a = b$$ follows.";
    MathRegionList regions = find_math_regions(text, strlen(text), arena);

    EXPECT_EQ(1, regions.count);
    if (regions.count >= 1) {
        EXPECT_TRUE(regions.regions[0].is_display);
    }
}

TEST_F(MathBridgeTest, FindBracketDisplayMath) {
    const char* text = "We have \\[x^2\\] here.";
    MathRegionList regions = find_math_regions(text, strlen(text), arena);

    EXPECT_EQ(1, regions.count);
    if (regions.count >= 1) {
        EXPECT_TRUE(regions.regions[0].is_display);
    }
}

TEST_F(MathBridgeTest, MultipleMathRegions) {
    const char* text = "Given $a$ and $b$, compute $a+b$.";
    MathRegionList regions = find_math_regions(text, strlen(text), arena);

    EXPECT_EQ(3, regions.count);
    for (int i = 0; i < regions.count; i++) {
        EXPECT_FALSE(regions.regions[i].is_display);
    }
}

TEST_F(MathBridgeTest, MixedMathRegions) {
    const char* text = "Inline $x$ then display $$y$$ then inline $z$.";
    MathRegionList regions = find_math_regions(text, strlen(text), arena);

    EXPECT_EQ(3, regions.count);
    if (regions.count >= 3) {
        EXPECT_FALSE(regions.regions[0].is_display);  // $x$
        EXPECT_TRUE(regions.regions[1].is_display);   // $$y$$
        EXPECT_FALSE(regions.regions[2].is_display);  // $z$
    }
}

TEST_F(MathBridgeTest, NoMathRegions) {
    const char* text = "No math here at all.";
    MathRegionList regions = find_math_regions(text, strlen(text), arena);

    EXPECT_EQ(0, regions.count);
}

// ============================================================================
// Display Math Tests
// ============================================================================

TEST_F(MathBridgeTest, TypesetDisplayMath) {
    auto ctx = create_context(10.0f);
    DisplayMathParams params = DisplayMathParams::defaults(300.0f);

    TexNode* result = typeset_display_math("a + b = c", ctx, params);

    ASSERT_NE(nullptr, result);
    EXPECT_EQ(NodeClass::VList, result->node_class);

    // Should be centered to line width
    EXPECT_FLOAT_EQ(300.0f, result->width);

    // Should have above and below spacing
    EXPECT_GT(result->height, 0);
    EXPECT_GT(result->depth, 0);
}

TEST_F(MathBridgeTest, DisplayMathCentering) {
    auto ctx = create_context(10.0f);
    DisplayMathParams params = DisplayMathParams::defaults(400.0f);

    TexNode* result = typeset_display_math("x", ctx, params);

    ASSERT_NE(nullptr, result);

    // The content should be centered - we can verify the structure
    EXPECT_EQ(NodeClass::VList, result->node_class);
}

// ============================================================================
// Text with Inline Math Tests
// ============================================================================

TEST_F(MathBridgeTest, ProcessTextWithMath) {
    auto ctx = create_context(10.0f);

    const char* text = "Let $x$ be a number.";
    TexNode* result = process_text_with_math(text, strlen(text), ctx, fonts);

    ASSERT_NE(nullptr, result);
    EXPECT_GT(result->width, 0);
}

TEST_F(MathBridgeTest, ProcessTextNoMath) {
    auto ctx = create_context(10.0f);

    const char* text = "No math here.";
    TexNode* result = process_text_with_math(text, strlen(text), ctx, fonts);

    ASSERT_NE(nullptr, result);
    EXPECT_GT(result->width, 0);
}

// ============================================================================
// Math Context Tests
// ============================================================================

TEST_F(MathBridgeTest, ContextCreation) {
    auto ctx = create_context(12.0f);

    EXPECT_EQ(arena, ctx.arena);
    EXPECT_EQ(fonts, ctx.fonts);
    EXPECT_FLOAT_EQ(12.0f, ctx.base_size_pt);
    EXPECT_EQ(MathStyle::Text, ctx.style);

    EXPECT_GT(ctx.x_height, 0);
    EXPECT_GT(ctx.quad, 0);
    EXPECT_GT(ctx.axis_height, 0);
    EXPECT_GT(ctx.rule_thickness, 0);
}

TEST_F(MathBridgeTest, ContextFontSize) {
    auto ctx = create_context(10.0f);

    ctx.style = MathStyle::Display;
    EXPECT_FLOAT_EQ(10.0f, ctx.font_size());

    ctx.style = MathStyle::Script;
    EXPECT_FLOAT_EQ(7.0f, ctx.font_size());  // 70%

    ctx.style = MathStyle::ScriptScript;
    EXPECT_FLOAT_EQ(5.0f, ctx.font_size());  // 50%
}

// ============================================================================
// Utility Function Tests
// ============================================================================

TEST_F(MathBridgeTest, MeasureMathWidth) {
    auto ctx = create_context(10.0f);

    TexNode* math = typeset_math_string("abc", 3, ctx);
    float width = measure_math_width(math);

    EXPECT_GT(width, 0);
    EXPECT_FLOAT_EQ(math->width, width);
}

TEST_F(MathBridgeTest, CenterMath) {
    auto ctx = create_context(10.0f);

    TexNode* content = typeset_math_string("x", 1, ctx);
    float content_width = content->width;

    TexNode* centered = center_math(content, 100.0f, arena);

    EXPECT_EQ(NodeClass::HBox, centered->node_class);
    EXPECT_FLOAT_EQ(100.0f, centered->width);

    // Content should be child
    bool found_content = false;
    for (TexNode* n = centered->first_child; n; n = n->next_sibling) {
        if (n == content) {
            found_content = true;
            break;
        }
    }
    EXPECT_TRUE(found_content);
}

TEST_F(MathBridgeTest, CenterMathWide) {
    // When content is wider than target, return as-is
    auto ctx = create_context(10.0f);

    TexNode* content = typeset_math_string("abcdefghijklmnop", 16, ctx);
    TexNode* result = center_math(content, 10.0f, arena);

    EXPECT_EQ(content, result);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(MathBridgeTest, QuadraticFormula) {
    auto ctx = create_context(10.0f);
    ctx.style = MathStyle::Display;

    // Build x = (-b ± sqrt(b² - 4ac)) / 2a
    // Simplified: test fraction with sqrt in numerator
    TexNode* b_squared = typeset_math_string("b", 1, ctx);
    TexNode* sqrt_content = typeset_sqrt(b_squared, ctx);

    ASSERT_NE(nullptr, sqrt_content);
    EXPECT_GT(sqrt_content->width, 0);
}

TEST_F(MathBridgeTest, NestedFractions) {
    auto ctx = create_context(10.0f);
    ctx.style = MathStyle::Display;

    // Build 1 / (1 + 1/2)
    TexNode* inner_frac = typeset_fraction_strings("1", "2", ctx);
    TexNode* one = typeset_math_string("1", 1, ctx);

    // Would need addition operator between one and inner_frac
    // For now just verify inner fraction is valid
    ASSERT_NE(nullptr, inner_frac);
    EXPECT_GT(inner_frac->width, 0);
}

TEST_F(MathBridgeTest, ScriptedFraction) {
    auto ctx = create_context(10.0f);

    // x^{\frac{1}{2}}
    TexNode* frac = typeset_fraction_strings("1", "2", ctx);
    TexNode* base = typeset_math_string("x", 1, ctx);

    TexNode* result = typeset_scripts(base, nullptr, frac, ctx);

    ASSERT_NE(nullptr, result);
    EXPECT_GT(result->width, base->width);
    EXPECT_GT(result->height, base->height);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    log_init("log.conf");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
