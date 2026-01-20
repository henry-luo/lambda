// test_latex_html_gtest.cpp - Unit tests for TeX HTML rendering
//
// Tests the HTML output functionality for math formulas

#include <gtest/gtest.h>
#include "lambda/tex/tex_html_render.hpp"
#include "lambda/tex/tex_math_bridge.hpp"
#include "lambda/tex/tex_math_ast.hpp"
#include "lambda/tex/tex_node.hpp"
#include "lambda/tex/tex_tfm.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include <cstring>

class LatexHtmlTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    tex::TFMFontManager* fonts;
    
    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
        fonts = tex::create_font_manager(arena);
    }
    
    void TearDown() override {
        arena_destroy(arena);
        pool_destroy(pool);
    }
    
    // helper to render math to HTML
    const char* render_math_html(const char* latex) {
        // Parse to AST
        tex::MathASTNode* ast = tex::parse_math_string_to_ast(latex, strlen(latex), arena);
        if (!ast) {
            return nullptr;
        }
        
        // Typeset
        tex::MathContext ctx = tex::MathContext::create(arena, fonts, 10.0f);
        ctx.style = tex::MathStyle::Display;
        tex::TexNode* node = tex::typeset_math_ast(ast, ctx);
        if (!node) {
            return nullptr;
        }
        
        // Render to HTML
        return tex::render_texnode_to_html(node, arena);
    }
};

TEST_F(LatexHtmlTest, SimpleVariable) {
    const char* html = render_math_html("x");
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "ML__latex") != nullptr) << "Should have wrapper class";
}

TEST_F(LatexHtmlTest, SimpleFraction) {
    const char* html = render_math_html("\\frac{a}{b}");
    ASSERT_NE(html, nullptr);
    // Note: typesetter converts Fraction nodes to VList with Rule
    // so we check for vlist and rule instead of mfrac
    EXPECT_TRUE(strstr(html, "ML__vlist") != nullptr || strstr(html, "ML__mfrac") != nullptr) 
        << "Should have vlist or fraction structure";
    EXPECT_TRUE(strstr(html, "ML__rule") != nullptr) << "Should have rule (fraction line)";
    EXPECT_TRUE(strstr(html, ">a<") != nullptr) << "Should contain numerator 'a'";
    EXPECT_TRUE(strstr(html, ">b<") != nullptr) << "Should contain denominator 'b'";
}

TEST_F(LatexHtmlTest, SquareRoot) {
    const char* html = render_math_html("\\sqrt{x}");
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "ML__sqrt") != nullptr) << "Should have sqrt class";
}

TEST_F(LatexHtmlTest, Superscript) {
    const char* html = render_math_html("x^2");
    ASSERT_NE(html, nullptr);
    // Should have scripts structure
    EXPECT_TRUE(strstr(html, "ML__") != nullptr);
}

TEST_F(LatexHtmlTest, Subscript) {
    const char* html = render_math_html("x_i");
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "ML__") != nullptr);
}

TEST_F(LatexHtmlTest, BinaryOperator) {
    const char* html = render_math_html("a + b");
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "ML__bin") != nullptr) << "Should have binary operator class";
}

TEST_F(LatexHtmlTest, RelationOperator) {
    const char* html = render_math_html("a = b");
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "ML__rel") != nullptr) << "Should have relation class";
}

TEST_F(LatexHtmlTest, StandaloneDocument) {
    // Parse to AST
    tex::MathASTNode* ast = tex::parse_math_string_to_ast("\\frac{1}{2}", 11, arena);
    ASSERT_NE(ast, nullptr);
    
    // Typeset
    tex::MathContext ctx = tex::MathContext::create(arena, fonts, 10.0f);
    tex::TexNode* node = tex::typeset_math_ast(ast, ctx);
    ASSERT_NE(node, nullptr);
    
    // Render as standalone document
    tex::HtmlRenderOptions opts;
    opts.standalone = true;
    
    const char* html = tex::render_texnode_to_html_document(node, arena, opts);
    ASSERT_NE(html, nullptr);
    
    // Should have full HTML structure
    EXPECT_TRUE(strstr(html, "<!DOCTYPE html>") != nullptr) << "Should have DOCTYPE";
    EXPECT_TRUE(strstr(html, "<html>") != nullptr) << "Should have html tag";
    EXPECT_TRUE(strstr(html, "<style>") != nullptr) << "Should have style tag";
    EXPECT_TRUE(strstr(html, "ML__mfrac") != nullptr) << "Should have fraction class";
}

TEST_F(LatexHtmlTest, CSSStylesheet) {
    const char* css = tex::get_math_css_stylesheet();
    ASSERT_NE(css, nullptr);
    
    // Should define MathLive-compatible classes
    EXPECT_TRUE(strstr(css, ".ML__latex") != nullptr);
    EXPECT_TRUE(strstr(css, ".ML__mfrac") != nullptr);
    EXPECT_TRUE(strstr(css, ".ML__sqrt") != nullptr);
    EXPECT_TRUE(strstr(css, ".ML__sup") != nullptr);
    EXPECT_TRUE(strstr(css, ".ML__sub") != nullptr);
}

TEST_F(LatexHtmlTest, ComplexFormula) {
    const char* html = render_math_html("\\frac{-b + \\sqrt{b^2 - 4ac}}{2a}");
    ASSERT_NE(html, nullptr);
    
    // Should have vertical list structure for fraction
    EXPECT_TRUE(strstr(html, "ML__vlist") != nullptr || strstr(html, "ML__mfrac") != nullptr)
        << "Should have vertical list or fraction structure";
    // Should have appropriate content length (complex formula generates significant HTML)
    EXPECT_GT(strlen(html), 200) << "Should have substantial HTML content";
}

TEST_F(LatexHtmlTest, GreekLetter) {
    const char* html = render_math_html("\\alpha");
    ASSERT_NE(html, nullptr);
    // The alpha character is rendered through the TFM font
    // which may produce a glyph code, not necessarily Unicode alpha
    // Just verify we got some output with the ML class
    EXPECT_TRUE(strstr(html, "ML__") != nullptr) << "Should have ML class";
}

TEST_F(LatexHtmlTest, Struts) {
    const char* html = render_math_html("x");
    ASSERT_NE(html, nullptr);
    // Should have struts for baseline alignment
    EXPECT_TRUE(strstr(html, "ML__strut") != nullptr) << "Should have struts";
}

TEST_F(LatexHtmlTest, CustomClassPrefix) {
    // Parse to AST
    tex::MathASTNode* ast = tex::parse_math_string_to_ast("x", 1, arena);
    ASSERT_NE(ast, nullptr);
    
    // Typeset
    tex::MathContext ctx = tex::MathContext::create(arena, fonts, 10.0f);
    tex::TexNode* node = tex::typeset_math_ast(ast, ctx);
    ASSERT_NE(node, nullptr);
    
    // Render with custom prefix
    tex::HtmlRenderOptions opts;
    opts.class_prefix = "MATH";
    
    const char* html = tex::render_texnode_to_html(node, arena, opts);
    ASSERT_NE(html, nullptr);
    
    // Should use custom prefix
    EXPECT_TRUE(strstr(html, "MATH__latex") != nullptr);
    EXPECT_TRUE(strstr(html, "ML__") == nullptr) << "Should not have default prefix";
}

TEST_F(LatexHtmlTest, NoStyles) {
    // Parse to AST
    tex::MathASTNode* ast = tex::parse_math_string_to_ast("x", 1, arena);
    ASSERT_NE(ast, nullptr);
    
    // Typeset
    tex::MathContext ctx = tex::MathContext::create(arena, fonts, 10.0f);
    tex::TexNode* node = tex::typeset_math_ast(ast, ctx);
    ASSERT_NE(node, nullptr);
    
    // Render without inline styles
    tex::HtmlRenderOptions opts;
    opts.include_styles = false;
    
    const char* html = tex::render_texnode_to_html(node, arena, opts);
    ASSERT_NE(html, nullptr);
    
    // Should still have classes but minimal styles
    EXPECT_TRUE(strstr(html, "class=\"") != nullptr);
}
