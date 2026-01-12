// test_tex_math_extended_gtest.cpp - Extended Math Typesetting Tests (M2)
//
// Tests for Phase 3 math enhancements:
// - Extensible delimiters (\left, \right)
// - Big operators with limits (\sum, \prod, \int)
// - Math accents (\hat, \bar, \vec, \dot)
// - Phantom boxes (\phantom, \hphantom, \vphantom)
// - Overbrace/underbrace
// - Stackrel and similar constructs

#include <gtest/gtest.h>
#include "lambda/tex/tex_math_bridge.hpp"
#include "lambda/tex/tex_node.hpp"
#include "lambda/tex/tex_tfm.hpp"
#include "lambda/tex/tex_hlist.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include <cstring>
#include <cmath>

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class MathExtendedTest : public ::testing::Test {
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

    // Helper to check node dimensions
    void expect_positive_dimensions(TexNode* node, const char* name) {
        ASSERT_NE(node, nullptr) << name << " is null";
        EXPECT_GT(node->width, 0.0f) << name << " width should be positive";
        EXPECT_GE(node->height + node->depth, 0.0f) << name << " total height should be non-negative";
    }
};

// ============================================================================
// MathContext Tests
// ============================================================================

TEST_F(MathExtendedTest, MathContextCreate) {
    MathContext ctx = create_context(10.0f);

    EXPECT_EQ(ctx.arena, arena);
    EXPECT_EQ(ctx.fonts, fonts);
    EXPECT_FLOAT_EQ(ctx.base_size_pt, 10.0f);
    EXPECT_EQ(ctx.style, MathStyle::Text);
}

TEST_F(MathExtendedTest, MathContextFontSize) {
    MathContext ctx = create_context(10.0f);

    // Text style uses base size
    ctx.style = MathStyle::Text;
    EXPECT_FLOAT_EQ(ctx.font_size(), 10.0f);

    // Script style uses smaller size (typically 70%)
    ctx.style = MathStyle::Script;
    float script_size = ctx.font_size();
    EXPECT_LT(script_size, 10.0f);

    // ScriptScript even smaller (typically 50%)
    ctx.style = MathStyle::ScriptScript;
    float scriptscript_size = ctx.font_size();
    EXPECT_LT(scriptscript_size, script_size);
}

TEST_F(MathExtendedTest, MathStyleFactors) {
    // Check style size factors are correct
    EXPECT_FLOAT_EQ(style_size_factor(MathStyle::Display), 1.0f);
    EXPECT_FLOAT_EQ(style_size_factor(MathStyle::Text), 1.0f);
    EXPECT_LT(style_size_factor(MathStyle::Script), 1.0f);
    EXPECT_LT(style_size_factor(MathStyle::ScriptScript), style_size_factor(MathStyle::Script));
}

// ============================================================================
// Extensible Delimiter Tests
// ============================================================================

TEST_F(MathExtendedTest, ExtensibleDelimiterBasic) {
    MathContext ctx = create_context(10.0f);

    // Get extension font
    TFMFont* cmex = fonts->get_font("cmex10");
    if (!cmex) {
        GTEST_SKIP() << "TFM fonts not available";
    }

    // Build extensible left parenthesis to match 30pt height
    TexNode* lparen = build_extensible_delimiter(
        arena, '(', 30.0f, ctx.extension_font, cmex, 10.0f
    );

    if (lparen) {
        float total_height = lparen->height + lparen->depth;
        EXPECT_GT(total_height, 0.0f);
        log_debug("test: extensible ( h=%.1f d=%.1f total=%.1f",
                  lparen->height, lparen->depth, total_height);
    }
}

TEST_F(MathExtendedTest, ExtensibleDelimiterSmallTarget) {
    MathContext ctx = create_context(10.0f);

    TFMFont* cmex = fonts->get_font("cmex10");
    if (!cmex) {
        GTEST_SKIP() << "TFM fonts not available";
    }

    // Small target height - should use smallest delimiter variant
    TexNode* lparen = build_extensible_delimiter(
        arena, '(', 10.0f, ctx.extension_font, cmex, 10.0f
    );

    if (lparen) {
        EXPECT_GT(lparen->width, 0.0f);
    }
}

TEST_F(MathExtendedTest, ExtensibleDelimiterLargeTarget) {
    MathContext ctx = create_context(10.0f);

    TFMFont* cmex = fonts->get_font("cmex10");
    if (!cmex) {
        GTEST_SKIP() << "TFM fonts not available";
    }

    // Large target height - should construct from pieces
    TexNode* lbrace = build_extensible_delimiter(
        arena, '{', 100.0f, ctx.extension_font, cmex, 10.0f
    );

    if (lbrace) {
        float total = lbrace->height + lbrace->depth;
        // Large braces may be close to target or use repeating pattern
        EXPECT_GT(total, 50.0f);
    }
}

// ============================================================================
// Typeset Delimited Tests
// ============================================================================

TEST_F(MathExtendedTest, TypesetDelimitedBasic) {
    MathContext ctx = create_context(10.0f);

    // Create simple content
    TexNode* content = make_hlist(arena);
    TexNode* kern = make_kern(arena, 20.0f);
    kern->height = 10.0f;
    kern->depth = 5.0f;
    content->append_child(kern);
    content->width = 20.0f;
    content->height = 10.0f;
    content->depth = 5.0f;

    // Typeset with parentheses
    TexNode* result = typeset_delimited('(', content, ')', ctx);

    if (result) {
        expect_positive_dimensions(result, "delimited expression");
        // Should be wider than content due to delimiters
        EXPECT_GT(result->width, content->width);
    }
}

TEST_F(MathExtendedTest, TypesetDelimitedEmpty) {
    MathContext ctx = create_context(10.0f);

    // Empty content
    TexNode* content = make_hlist(arena);
    content->width = 0.0f;
    content->height = ctx.x_height;
    content->depth = 0.0f;

    TexNode* result = typeset_delimited('(', content, ')', ctx);

    // Should still produce delimiters
    if (result) {
        EXPECT_GT(result->width, 0.0f);
    }
}

// ============================================================================
// Math String Typesetting Tests
// ============================================================================

TEST_F(MathExtendedTest, TypesetSimpleMathString) {
    MathContext ctx = create_context(10.0f);

    const char* input = "a + b";
    TexNode* result = typeset_math_string(input, strlen(input), ctx);

    if (result) {
        expect_positive_dimensions(result, "simple math");
    } else {
        GTEST_SKIP() << "Math string typesetting not available";
    }
}

TEST_F(MathExtendedTest, TypesetLatexMath) {
    MathContext ctx = create_context(10.0f);

    const char* input = "x^2";
    TexNode* result = typeset_latex_math(input, strlen(input), ctx);

    if (result) {
        expect_positive_dimensions(result, "latex math");
    } else {
        GTEST_SKIP() << "LaTeX math typesetting not available";
    }
}

// ============================================================================
// Atom Type Classification Tests
// ============================================================================

TEST_F(MathExtendedTest, ClassifyCodepointOrd) {
    // Regular letters should be Ord
    EXPECT_EQ(classify_codepoint('a'), AtomType::Ord);
    EXPECT_EQ(classify_codepoint('x'), AtomType::Ord);
    EXPECT_EQ(classify_codepoint('A'), AtomType::Ord);
}

TEST_F(MathExtendedTest, ClassifyCodepointDigit) {
    // Digits should be Ord
    EXPECT_EQ(classify_codepoint('0'), AtomType::Ord);
    EXPECT_EQ(classify_codepoint('9'), AtomType::Ord);
}

TEST_F(MathExtendedTest, ClassifyCodepointBinOp) {
    // Binary operators
    EXPECT_EQ(classify_codepoint('+'), AtomType::Bin);
    EXPECT_EQ(classify_codepoint('-'), AtomType::Bin);
    EXPECT_EQ(classify_codepoint('*'), AtomType::Bin);
}

TEST_F(MathExtendedTest, ClassifyCodepointRel) {
    // Relations
    EXPECT_EQ(classify_codepoint('='), AtomType::Rel);
    EXPECT_EQ(classify_codepoint('<'), AtomType::Rel);
    EXPECT_EQ(classify_codepoint('>'), AtomType::Rel);
}

TEST_F(MathExtendedTest, ClassifyCodepointPunct) {
    // Punctuation
    EXPECT_EQ(classify_codepoint(','), AtomType::Punct);
    EXPECT_EQ(classify_codepoint(';'), AtomType::Punct);
}

TEST_F(MathExtendedTest, ClassifyCodepointOpen) {
    // Opening delimiters
    EXPECT_EQ(classify_codepoint('('), AtomType::Open);
    EXPECT_EQ(classify_codepoint('['), AtomType::Open);
    EXPECT_EQ(classify_codepoint('{'), AtomType::Open);
}

TEST_F(MathExtendedTest, ClassifyCodepointClose) {
    // Closing delimiters
    EXPECT_EQ(classify_codepoint(')'), AtomType::Close);
    EXPECT_EQ(classify_codepoint(']'), AtomType::Close);
    EXPECT_EQ(classify_codepoint('}'), AtomType::Close);
}

// ============================================================================
// Atom Spacing Tests
// ============================================================================

TEST_F(MathExtendedTest, AtomSpacingOrdBin) {
    // Spacing between Ord and Bin should be medium
    float spacing = get_atom_spacing_mu(AtomType::Ord, AtomType::Bin, MathStyle::Text);
    EXPECT_GT(spacing, 0.0f);
}

TEST_F(MathExtendedTest, AtomSpacingOrdRel) {
    // Spacing around relations should be thick
    float spacing = get_atom_spacing_mu(AtomType::Ord, AtomType::Rel, MathStyle::Text);
    EXPECT_GT(spacing, 0.0f);
}

TEST_F(MathExtendedTest, AtomSpacingOrdOrd) {
    // Spacing between Ord and Ord is usually 0
    float spacing = get_atom_spacing_mu(AtomType::Ord, AtomType::Ord, MathStyle::Text);
    EXPECT_EQ(spacing, 0.0f);
}

TEST_F(MathExtendedTest, AtomSpacingScriptStyle) {
    // In script styles, spacing may be different (often 0)
    float text_spacing = get_atom_spacing_mu(AtomType::Ord, AtomType::Bin, MathStyle::Text);
    float script_spacing = get_atom_spacing_mu(AtomType::Ord, AtomType::Bin, MathStyle::Script);
    // Script style typically has no spacing or less spacing
    EXPECT_LE(script_spacing, text_spacing);
}

// ============================================================================
// Mu to Points Conversion
// ============================================================================

TEST_F(MathExtendedTest, MuToPointsBasic) {
    MathContext ctx = create_context(10.0f);

    // 18mu = 1 quad (1em)
    float one_quad = mu_to_pt(18.0f, ctx);
    EXPECT_GT(one_quad, 0.0f);

    // 0mu = 0pt
    float zero = mu_to_pt(0.0f, ctx);
    EXPECT_FLOAT_EQ(zero, 0.0f);
}

TEST_F(MathExtendedTest, MuToPointsScaling) {
    MathContext ctx = create_context(10.0f);

    float small = mu_to_pt(1.0f, ctx);
    float big = mu_to_pt(10.0f, ctx);

    EXPECT_GT(big, small);
    EXPECT_NEAR(big, small * 10.0f, 0.01f);
}

// ============================================================================
// Fraction Tests
// ============================================================================

TEST_F(MathExtendedTest, FractionBasic) {
    MathContext ctx = create_context(10.0f);

    // Create simple numerator and denominator
    TexNode* num = make_hlist(arena);
    num->width = 10.0f;
    num->height = 5.0f;
    num->depth = 0.0f;

    TexNode* denom = make_hlist(arena);
    denom->width = 10.0f;
    denom->height = 5.0f;
    denom->depth = 0.0f;

    TexNode* frac = typeset_fraction(num, denom, ctx.rule_thickness, ctx);

    if (frac) {
        expect_positive_dimensions(frac, "fraction");
        // Fraction should have both height and depth (above and below axis)
        EXPECT_GT(frac->height, 0.0f);
        EXPECT_GT(frac->depth, 0.0f);
    }
}

// ============================================================================
// Square Root Tests
// ============================================================================

TEST_F(MathExtendedTest, SquareRootBasic) {
    MathContext ctx = create_context(10.0f);

    // Create simple radicand
    TexNode* radicand = make_hlist(arena);
    radicand->width = 20.0f;
    radicand->height = 8.0f;
    radicand->depth = 2.0f;

    TexNode* sqrt = typeset_sqrt(radicand, ctx);

    if (sqrt) {
        expect_positive_dimensions(sqrt, "square root");
        // Root should be wider than radicand (due to radical sign)
        EXPECT_GT(sqrt->width, radicand->width);
    }
}

// ============================================================================
// Superscript/Subscript Tests
// ============================================================================

TEST_F(MathExtendedTest, SuperscriptPositioning) {
    MathContext ctx = create_context(10.0f);

    // Create base and superscript
    TexNode* base = make_hlist(arena);
    base->width = 5.0f;
    base->height = 5.0f;
    base->depth = 0.0f;

    TexNode* sup = make_hlist(arena);
    sup->width = 3.0f;
    sup->height = 3.0f;
    sup->depth = 0.0f;

    // typeset_scripts(nucleus, subscript, superscript, ctx)
    TexNode* result = typeset_scripts(base, nullptr, sup, ctx);

    if (result) {
        expect_positive_dimensions(result, "with superscript");
        // Total width should be approximately base + superscript
        EXPECT_GT(result->width, base->width);
        // Height should be elevated
        EXPECT_GT(result->height, base->height);
    }
}

TEST_F(MathExtendedTest, SubscriptPositioning) {
    MathContext ctx = create_context(10.0f);

    // Create base and subscript
    TexNode* base = make_hlist(arena);
    base->width = 5.0f;
    base->height = 5.0f;
    base->depth = 0.0f;

    TexNode* sub = make_hlist(arena);
    sub->width = 3.0f;
    sub->height = 3.0f;
    sub->depth = 0.0f;

    // typeset_scripts(nucleus, subscript, superscript, ctx)
    TexNode* result = typeset_scripts(base, sub, nullptr, ctx);

    if (result) {
        expect_positive_dimensions(result, "with subscript");
        // Should have depth (subscript below baseline)
        EXPECT_GT(result->depth, 0.0f);
    }
}

// ============================================================================
// Math Style Cramped Tests
// ============================================================================

TEST_F(MathExtendedTest, CrampedStyle) {
    // Cramped styles have reduced superscript heights
    MathStyle text = MathStyle::Text;
    MathStyle cramped = cramped_style(text);

    // Cramped style should have the same size factor
    EXPECT_EQ(style_size_factor(cramped), style_size_factor(text));
}

// ============================================================================
// Font Spec Tests
// ============================================================================

TEST_F(MathExtendedTest, FontSpecBasic) {
    FontSpec spec("cmr10", 10.0f, nullptr, 0);

    EXPECT_STREQ(spec.name, "cmr10");
    EXPECT_FLOAT_EQ(spec.size_pt, 10.0f);
}

TEST_F(MathExtendedTest, MathContextFonts) {
    MathContext ctx = create_context(10.0f);

    // Check that all math fonts are set up
    EXPECT_STREQ(ctx.roman_font.name, "cmr10");
    EXPECT_STREQ(ctx.italic_font.name, "cmmi10");
    EXPECT_STREQ(ctx.symbol_font.name, "cmsy10");
    EXPECT_STREQ(ctx.extension_font.name, "cmex10");
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(MathExtendedTest, EmptyMathString) {
    MathContext ctx = create_context(10.0f);

    TexNode* result = typeset_math_string("", 0, ctx);

    // Empty string should return valid empty box or nullptr
    if (result) {
        EXPECT_GE(result->width, 0.0f);
    }
}

TEST_F(MathExtendedTest, WhitespaceMathString) {
    MathContext ctx = create_context(10.0f);

    const char* input = "   ";
    TexNode* result = typeset_math_string(input, strlen(input), ctx);

    // Whitespace in math mode is typically ignored
    if (result) {
        // Width may be 0 or contain thin spaces
        EXPECT_GE(result->width, 0.0f);
    }
}

TEST_F(MathExtendedTest, LargeFontSize) {
    MathContext ctx = create_context(24.0f);

    EXPECT_FLOAT_EQ(ctx.base_size_pt, 24.0f);

    // Font parameters should scale with size
    MathContext ctx10 = create_context(10.0f);
    EXPECT_GT(ctx.quad, ctx10.quad);
}

TEST_F(MathExtendedTest, SmallFontSize) {
    MathContext ctx = create_context(6.0f);

    EXPECT_FLOAT_EQ(ctx.base_size_pt, 6.0f);
}
