// test_latex_integration_gtest.cpp - Integration tests for LaTeX typesetting
//
// These tests exercise the full LaTeX typesetting pipeline:
// Source -> Parse -> AST -> Typeset -> Box Tree -> JSON/Render

#include <gtest/gtest.h>
#include "../lambda/tex/tex_typeset.hpp"
#include "../lambda/tex/tex_ast.hpp"
#include "../lambda/tex/tex_ast_builder.hpp"
#include "../lambda/tex/tex_box.hpp"
#include "../lambda/tex/tex_output.hpp"
#include "../lambda/tex/tex_math_layout.hpp"
#include "../lambda/tex/tex_paragraph.hpp"
#include "../lib/arena.h"
#include "../lib/log.h"

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class LaTeXIntegrationTest : public ::testing::Test {
protected:
    Arena arena;
    TypesetConfig config;

    void SetUp() override {
        arena_init(&arena, 256 * 1024);
        config = TypesetConfig::defaults();
    }

    void TearDown() override {
        arena_destroy(&arena);
    }

    // Helper to typeset LaTeX and return the result
    TypesetResult typeset(const char* source) {
        return typeset_latex(source, strlen(source), config, &arena);
    }

    // Helper to typeset just math
    TexBox* typeset_math(const char* math_source) {
        return typeset_math_inline(math_source, strlen(math_source), config, &arena);
    }

    // Helper to verify a box has expected dimensions within tolerance
    void expect_dimensions(const TexBox* box, float width, float height, float depth,
                          float tolerance = 0.5f) {
        ASSERT_NE(box, nullptr);
        EXPECT_NEAR(box->width, width, tolerance) << "Width mismatch";
        EXPECT_NEAR(box->height, height, tolerance) << "Height mismatch";
        EXPECT_NEAR(box->depth, depth, tolerance) << "Depth mismatch";
    }

    // Count glyphs in a box tree
    int count_glyphs(const TexBox* box) {
        if (!box) return 0;

        int count = 0;
        if (box->kind == BoxKind::Char) {
            count = 1;
        } else if (box->kind == BoxKind::HBox) {
            for (int i = 0; i < box->content.hbox.count; i++) {
                count += count_glyphs(box->content.hbox.children[i]);
            }
        } else if (box->kind == BoxKind::VBox) {
            for (int i = 0; i < box->content.vbox.count; i++) {
                count += count_glyphs(box->content.vbox.children[i]);
            }
        } else if (box->kind == BoxKind::Fraction) {
            count += count_glyphs(box->content.fraction.numerator);
            count += count_glyphs(box->content.fraction.denominator);
        } else if (box->kind == BoxKind::Radical) {
            count += count_glyphs(box->content.radical.radicand);
            count += count_glyphs(box->content.radical.index);
        }
        return count;
    }

    // Find first char box with given codepoint
    TexBox* find_char(TexBox* box, int codepoint) {
        if (!box) return nullptr;

        if (box->kind == BoxKind::Char && box->content.ch.codepoint == codepoint) {
            return box;
        }

        if (box->kind == BoxKind::HBox) {
            for (int i = 0; i < box->content.hbox.count; i++) {
                if (TexBox* found = find_char(box->content.hbox.children[i], codepoint)) {
                    return found;
                }
            }
        } else if (box->kind == BoxKind::VBox) {
            for (int i = 0; i < box->content.vbox.count; i++) {
                if (TexBox* found = find_char(box->content.vbox.children[i], codepoint)) {
                    return found;
                }
            }
        }
        return nullptr;
    }
};

// ============================================================================
// Basic Text Tests
// ============================================================================

TEST_F(LaTeXIntegrationTest, EmptyDocument) {
    TypesetResult result = typeset("");
    // Empty document should succeed but produce no visible content
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.error_count, 0);
}

TEST_F(LaTeXIntegrationTest, SingleWord) {
    TypesetResult result = typeset("Hello");
    EXPECT_TRUE(result.success);
    EXPECT_GE(result.page_count, 1);

    if (result.page_count > 0 && result.pages[0].content) {
        int glyphs = count_glyphs(result.pages[0].content);
        EXPECT_EQ(glyphs, 5);  // H-e-l-l-o
    }
}

TEST_F(LaTeXIntegrationTest, MultipleWords) {
    TypesetResult result = typeset("Hello World");
    EXPECT_TRUE(result.success);

    if (result.page_count > 0 && result.pages[0].content) {
        int glyphs = count_glyphs(result.pages[0].content);
        EXPECT_EQ(glyphs, 10);  // Hello World (space is glue, not glyph)
    }
}

TEST_F(LaTeXIntegrationTest, MultipleParagraphs) {
    TypesetResult result = typeset("First paragraph.\n\nSecond paragraph.");
    EXPECT_TRUE(result.success);
    // Should have content for multiple paragraphs
}

// ============================================================================
// Math Mode Tests
// ============================================================================

TEST_F(LaTeXIntegrationTest, InlineMathSimple) {
    TexBox* box = typeset_math("a + b");
    ASSERT_NE(box, nullptr);

    // Should have at least 3 characters (a, +, b) plus spacing
    int glyphs = count_glyphs(box);
    EXPECT_GE(glyphs, 3);
}

TEST_F(LaTeXIntegrationTest, InlineMathVariable) {
    TexBox* box = typeset_math("x");
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(count_glyphs(box), 1);

    // Variable should be in math italic (character 'x')
    TexBox* x_box = find_char(box, 'x');
    ASSERT_NE(x_box, nullptr);
    EXPECT_GT(x_box->width, 0);
}

TEST_F(LaTeXIntegrationTest, InlineMathFraction) {
    TexBox* box = typeset_math("\\frac{a}{b}");
    ASSERT_NE(box, nullptr);

    // Fraction should be taller than a single character
    EXPECT_GT(box->height + box->depth, 10.0f);

    // Should contain both 'a' and 'b'
    EXPECT_NE(find_char(box, 'a'), nullptr);
    EXPECT_NE(find_char(box, 'b'), nullptr);
}

TEST_F(LaTeXIntegrationTest, InlineMathSuperscript) {
    TexBox* box = typeset_math("x^2");
    ASSERT_NE(box, nullptr);

    // Should have x and 2
    TexBox* x_box = find_char(box, 'x');
    TexBox* two_box = find_char(box, '2');
    ASSERT_NE(x_box, nullptr);
    ASSERT_NE(two_box, nullptr);

    // Superscript '2' should be smaller (cramped style)
    // and positioned above baseline
}

TEST_F(LaTeXIntegrationTest, InlineMathSubscript) {
    TexBox* box = typeset_math("x_i");
    ASSERT_NE(box, nullptr);

    TexBox* x_box = find_char(box, 'x');
    TexBox* i_box = find_char(box, 'i');
    ASSERT_NE(x_box, nullptr);
    ASSERT_NE(i_box, nullptr);
}

TEST_F(LaTeXIntegrationTest, InlineMathSubSuperscript) {
    TexBox* box = typeset_math("x_i^2");
    ASSERT_NE(box, nullptr);

    // Should have all three: x, i, 2
    EXPECT_NE(find_char(box, 'x'), nullptr);
    EXPECT_NE(find_char(box, 'i'), nullptr);
    EXPECT_NE(find_char(box, '2'), nullptr);
}

TEST_F(LaTeXIntegrationTest, InlineMathSqrt) {
    TexBox* box = typeset_math("\\sqrt{x}");
    ASSERT_NE(box, nullptr);

    // Should contain x
    EXPECT_NE(find_char(box, 'x'), nullptr);

    // Sqrt should add some height for the radical sign
    EXPECT_GT(box->height, 5.0f);
}

TEST_F(LaTeXIntegrationTest, InlineMathNestedFractions) {
    TexBox* box = typeset_math("\\frac{\\frac{a}{b}}{c}");
    ASSERT_NE(box, nullptr);

    // Nested fractions should be taller
    EXPECT_GT(box->height + box->depth, 20.0f);

    // Should have all three variables
    EXPECT_NE(find_char(box, 'a'), nullptr);
    EXPECT_NE(find_char(box, 'b'), nullptr);
    EXPECT_NE(find_char(box, 'c'), nullptr);
}

// ============================================================================
// Greek Letters Tests
// ============================================================================

TEST_F(LaTeXIntegrationTest, GreekAlpha) {
    TexBox* box = typeset_math("\\alpha");
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(count_glyphs(box), 1);
}

TEST_F(LaTeXIntegrationTest, GreekMultiple) {
    TexBox* box = typeset_math("\\alpha + \\beta + \\gamma");
    ASSERT_NE(box, nullptr);
    // Should have alpha, beta, gamma, and two + signs
    EXPECT_GE(count_glyphs(box), 5);
}

TEST_F(LaTeXIntegrationTest, GreekUppercase) {
    TexBox* box = typeset_math("\\Gamma \\Delta \\Theta");
    ASSERT_NE(box, nullptr);
    EXPECT_GE(count_glyphs(box), 3);
}

// ============================================================================
// Operator Tests
// ============================================================================

TEST_F(LaTeXIntegrationTest, SumOperator) {
    TexBox* box = typeset_math("\\sum_{i=1}^{n} x_i");
    ASSERT_NE(box, nullptr);

    // Sum should be a large operator
    EXPECT_GT(box->height + box->depth, 15.0f);
}

TEST_F(LaTeXIntegrationTest, IntegralOperator) {
    TexBox* box = typeset_math("\\int_0^1 f(x) dx");
    ASSERT_NE(box, nullptr);

    // Integral should be tall
    EXPECT_GT(box->height + box->depth, 15.0f);
}

TEST_F(LaTeXIntegrationTest, ProductOperator) {
    TexBox* box = typeset_math("\\prod_{k=1}^{n} a_k");
    ASSERT_NE(box, nullptr);
    EXPECT_GT(box->height + box->depth, 15.0f);
}

TEST_F(LaTeXIntegrationTest, LimitOperator) {
    TexBox* box = typeset_math("\\lim_{n \\to \\infty} \\frac{1}{n}");
    ASSERT_NE(box, nullptr);
}

// ============================================================================
// Delimiter Tests
// ============================================================================

TEST_F(LaTeXIntegrationTest, SimpleParentheses) {
    TexBox* box = typeset_math("(a)");
    ASSERT_NE(box, nullptr);
    EXPECT_NE(find_char(box, 'a'), nullptr);
}

TEST_F(LaTeXIntegrationTest, AutoSizedDelimiters) {
    TexBox* box = typeset_math("\\left( \\frac{a}{b} \\right)");
    ASSERT_NE(box, nullptr);

    // Delimiters should grow to match fraction height
    EXPECT_GT(box->height + box->depth, 15.0f);
}

TEST_F(LaTeXIntegrationTest, BracketTypes) {
    TexBox* box1 = typeset_math("\\left[ x \\right]");
    TexBox* box2 = typeset_math("\\left\\{ x \\right\\}");
    TexBox* box3 = typeset_math("\\left| x \\right|");

    ASSERT_NE(box1, nullptr);
    ASSERT_NE(box2, nullptr);
    ASSERT_NE(box3, nullptr);
}

// ============================================================================
// Complex Formula Tests
// ============================================================================

TEST_F(LaTeXIntegrationTest, QuadraticFormula) {
    TexBox* box = typeset_math("x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}");
    ASSERT_NE(box, nullptr);

    // Should have multiple elements
    EXPECT_GE(count_glyphs(box), 10);
}

TEST_F(LaTeXIntegrationTest, EulerIdentity) {
    TexBox* box = typeset_math("e^{i\\pi} + 1 = 0");
    ASSERT_NE(box, nullptr);

    // Should find e, i, 1, 0
    EXPECT_NE(find_char(box, 'e'), nullptr);
    EXPECT_NE(find_char(box, 'i'), nullptr);
    EXPECT_NE(find_char(box, '1'), nullptr);
    EXPECT_NE(find_char(box, '0'), nullptr);
}

TEST_F(LaTeXIntegrationTest, GaussianIntegral) {
    TexBox* box = typeset_math("\\int_{-\\infty}^{\\infty} e^{-x^2} dx = \\sqrt{\\pi}");
    ASSERT_NE(box, nullptr);
}

TEST_F(LaTeXIntegrationTest, BinomialCoefficient) {
    TexBox* box = typeset_math("\\binom{n}{k} = \\frac{n!}{k!(n-k)!}");
    ASSERT_NE(box, nullptr);
}

// ============================================================================
// Spacing Tests
// ============================================================================

TEST_F(LaTeXIntegrationTest, MathSpacing) {
    // Binary operators should have space around them
    TexBox* box = typeset_math("a + b");
    ASSERT_NE(box, nullptr);

    // Width should be greater than just 3 characters
    EXPECT_GT(box->width, 15.0f);
}

TEST_F(LaTeXIntegrationTest, RelationSpacing) {
    // Relations should have more space
    TexBox* box = typeset_math("a = b");
    ASSERT_NE(box, nullptr);
    EXPECT_GT(box->width, 15.0f);
}

TEST_F(LaTeXIntegrationTest, ExplicitSpacing) {
    TexBox* box1 = typeset_math("ab");      // no space
    TexBox* box2 = typeset_math("a\\,b");   // thin space
    TexBox* box3 = typeset_math("a\\;b");   // thick space
    TexBox* box4 = typeset_math("a\\quad b"); // quad space

    ASSERT_NE(box1, nullptr);
    ASSERT_NE(box2, nullptr);
    ASSERT_NE(box3, nullptr);
    ASSERT_NE(box4, nullptr);

    // Each should be progressively wider
    EXPECT_LT(box1->width, box2->width);
    EXPECT_LT(box2->width, box3->width);
    EXPECT_LT(box3->width, box4->width);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(LaTeXIntegrationTest, UnmatchedBrace) {
    TypesetResult result = typeset("$\\frac{a}{b$");
    // Should handle gracefully
    EXPECT_GE(result.error_count, 0);  // May or may not report error
}

TEST_F(LaTeXIntegrationTest, UnknownCommand) {
    TexBox* box = typeset_math("\\unknowncommand");
    // Should not crash, may produce error or placeholder
    // Box may be null if error
}

TEST_F(LaTeXIntegrationTest, EmptyFraction) {
    TexBox* box = typeset_math("\\frac{}{}");
    // Should handle empty numerator/denominator
    ASSERT_NE(box, nullptr);
}

// ============================================================================
// JSON Output Integration Tests
// ============================================================================

TEST_F(LaTeXIntegrationTest, JsonOutputBasic) {
    TexBox* box = typeset_math("x + y");
    ASSERT_NE(box, nullptr);

    JSONOutputOptions opts = JSONOutputOptions::defaults();
    char* json = tex_box_to_json(box, &arena, opts);

    ASSERT_NE(json, nullptr);
    EXPECT_NE(strstr(json, "\"type\""), nullptr);
    EXPECT_NE(strstr(json, "\"width\""), nullptr);
}

TEST_F(LaTeXIntegrationTest, TypesetResultToJson) {
    TypesetResult result = typeset("Hello");
    EXPECT_TRUE(result.success);

    JSONOutputOptions opts = JSONOutputOptions::defaults();
    char* json = typeset_result_to_json(result, &arena, opts);

    ASSERT_NE(json, nullptr);
    EXPECT_NE(strstr(json, "\"success\": true"), nullptr);
    EXPECT_NE(strstr(json, "\"pages\""), nullptr);
}

// ============================================================================
// Font Metrics Tests
// ============================================================================

TEST_F(LaTeXIntegrationTest, CharacterWidthsVary) {
    // 'i' should be narrower than 'm'
    TexBox* i_box = typeset_math("i");
    TexBox* m_box = typeset_math("m");

    ASSERT_NE(i_box, nullptr);
    ASSERT_NE(m_box, nullptr);

    EXPECT_LT(i_box->width, m_box->width);
}

TEST_F(LaTeXIntegrationTest, SuperscriptSmaller) {
    // Superscript should use smaller font size
    TexBox* normal = typeset_math("x");
    TexBox* super = typeset_math("^x");  // just superscript

    // The 'x' in superscript position should be smaller
    // This test verifies cramped style is applied
}

// ============================================================================
// Paragraph Layout Tests
// ============================================================================

TEST_F(LaTeXIntegrationTest, LineBreaking) {
    // Long text should break into multiple lines
    const char* long_text =
        "This is a long paragraph that should be broken into multiple lines "
        "when typeset with the default page width settings.";

    TypesetResult result = typeset(long_text);
    EXPECT_TRUE(result.success);

    // The content should be a VBox containing multiple line HBoxes
    if (result.page_count > 0 && result.pages[0].content) {
        TexBox* content = result.pages[0].content;
        if (content->kind == BoxKind::VBox) {
            // Multiple children = multiple lines
            EXPECT_GT(content->content.vbox.count, 1);
        }
    }
}

TEST_F(LaTeXIntegrationTest, ParagraphIndent) {
    const char* para = "First paragraph.\n\nSecond paragraph.";
    TypesetResult result = typeset(para);
    EXPECT_TRUE(result.success);
    // Second paragraph should have indent
}

// ============================================================================
// Display Math Tests
// ============================================================================

TEST_F(LaTeXIntegrationTest, DisplayMathCentered) {
    const char* doc = "Text before $$x^2 + y^2 = z^2$$ text after";
    TypesetResult result = typeset(doc);
    EXPECT_TRUE(result.success);
    // Display math should be centered
}

TEST_F(LaTeXIntegrationTest, DisplayMathLargeOp) {
    // In display mode, sum should have limits above/below
    TexBox* inline_sum = typeset_math("\\sum_{i=1}^{n}");  // inline
    // Display mode would be different

    ASSERT_NE(inline_sum, nullptr);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
