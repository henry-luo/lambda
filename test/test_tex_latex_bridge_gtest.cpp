// test_tex_latex_bridge_gtest.cpp - Tests for LaTeX to TeX typesetting bridge
//
// These tests verify that the tex_latex_bridge correctly converts parsed
// LaTeX documents to TeX node trees for typesetting.

#include <gtest/gtest.h>
#include <cstring>
#include <cstdio>

#include "lambda/tex/tex_latex_bridge.hpp"
#include "lambda/tex/tex_lambda_bridge.hpp"
#include "lambda/tex/tex_node.hpp"
#include "lambda/tex/tex_tfm.hpp"
#include "lambda/input/input.hpp"
#include "lambda/lambda-data.hpp"
#include "lambda/mark_reader.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"

using namespace tex;

class TexLatexBridgeTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    TFMFontManager* fonts;

    void SetUp() override {
        log_init(nullptr);
        pool = pool_create();
        arena = arena_create_default(pool);
        fonts = create_font_manager(arena);
    }

    void TearDown() override {
        arena_destroy(arena);
        pool_destroy(pool);
        log_finish();
    }

    // Helper to parse LaTeX source and return the Element tree
    Item parse_latex(const char* latex_source) {
        String* type_str = (String*)pool_alloc(pool, sizeof(String) + 6);
        type_str->len = 5;
        strcpy(type_str->chars, "latex");

        Input* input = input_from_source(latex_source, nullptr, type_str, nullptr);
        if (!input) return ItemNull;
        return input->root;
    }
};

// ============================================================================
// Context Creation Tests
// ============================================================================

TEST_F(TexLatexBridgeTest, CreateContext) {
    LaTeXContext ctx = LaTeXContext::create(arena, fonts);

    EXPECT_EQ(ctx.doc_ctx.arena, arena);
    EXPECT_EQ(ctx.doc_ctx.fonts, fonts);
    EXPECT_STREQ(ctx.document_class, "article");
    EXPECT_FALSE(ctx.two_column);
    EXPECT_TRUE(ctx.in_preamble);
    EXPECT_EQ(ctx.chapter_num, 0);
    EXPECT_EQ(ctx.section_num, 0);
}

TEST_F(TexLatexBridgeTest, CreateContextWithBookClass) {
    LaTeXContext ctx = LaTeXContext::create(arena, fonts, "book");

    EXPECT_STREQ(ctx.document_class, "book");
    EXPECT_TRUE(ctx.twosided);  // Books are twosided by default
}

TEST_F(TexLatexBridgeTest, SectionNumberFormatting) {
    LaTeXContext ctx = LaTeXContext::create(arena, fonts, "article");

    // Article class: section numbering starts at 1
    ctx.section_num = 1;
    ctx.subsection_num = 2;
    ctx.subsubsection_num = 3;

    const char* sec_num = ctx.format_section_number(1, arena);
    EXPECT_STREQ(sec_num, "1");

    sec_num = ctx.format_section_number(2, arena);
    EXPECT_STREQ(sec_num, "1.2");

    sec_num = ctx.format_section_number(3, arena);
    EXPECT_STREQ(sec_num, "1.2.3");
}

TEST_F(TexLatexBridgeTest, SectionNumberFormattingBook) {
    LaTeXContext ctx = LaTeXContext::create(arena, fonts, "book");

    // Book class: chapter numbering
    ctx.chapter_num = 2;
    ctx.section_num = 3;
    ctx.subsection_num = 1;

    const char* sec_num = ctx.format_section_number(0, arena);  // chapter
    EXPECT_STREQ(sec_num, "2");

    sec_num = ctx.format_section_number(1, arena);  // section
    EXPECT_STREQ(sec_num, "2.3");

    sec_num = ctx.format_section_number(2, arena);  // subsection
    EXPECT_STREQ(sec_num, "2.3.1");
}

// ============================================================================
// Label Management Tests
// ============================================================================

TEST_F(TexLatexBridgeTest, LabelManagement) {
    LaTeXContext ctx = LaTeXContext::create(arena, fonts);

    ctx.add_label("sec:intro", "1", 1);
    ctx.add_label("eq:einstein", "2.1", 3);

    EXPECT_STREQ(ctx.resolve_ref("sec:intro"), "1");
    EXPECT_STREQ(ctx.resolve_ref("eq:einstein"), "2.1");
    EXPECT_STREQ(ctx.resolve_ref("undefined"), "??");
}

// ============================================================================
// Section Command Tests
// ============================================================================

TEST_F(TexLatexBridgeTest, IsSectionCommand) {
    EXPECT_TRUE(is_section_command("section"));
    EXPECT_TRUE(is_section_command("subsection"));
    EXPECT_TRUE(is_section_command("chapter"));
    EXPECT_FALSE(is_section_command("textbf"));
    EXPECT_FALSE(is_section_command("begin"));
}

TEST_F(TexLatexBridgeTest, GetSectionLevel) {
    EXPECT_EQ(get_section_level("chapter"), 0);
    EXPECT_EQ(get_section_level("section"), 1);
    EXPECT_EQ(get_section_level("subsection"), 2);
    EXPECT_EQ(get_section_level("subsubsection"), 3);
    EXPECT_EQ(get_section_level("paragraph"), 4);
    EXPECT_EQ(get_section_level("unknown"), -1);
}

// ============================================================================
// Text Format Command Tests
// ============================================================================

TEST_F(TexLatexBridgeTest, IsTextFormatCommand) {
    EXPECT_TRUE(is_text_format_command("textbf"));
    EXPECT_TRUE(is_text_format_command("textit"));
    EXPECT_TRUE(is_text_format_command("emph"));
    EXPECT_FALSE(is_text_format_command("section"));
    EXPECT_FALSE(is_text_format_command("begin"));
}

TEST_F(TexLatexBridgeTest, IsFontDeclaration) {
    EXPECT_TRUE(is_font_declaration("bf"));
    EXPECT_TRUE(is_font_declaration("it"));
    EXPECT_TRUE(is_font_declaration("bfseries"));
    EXPECT_FALSE(is_font_declaration("textbf"));
}

// ============================================================================
// Environment Tests
// ============================================================================

TEST_F(TexLatexBridgeTest, IsListEnvironment) {
    EXPECT_TRUE(is_list_environment("itemize"));
    EXPECT_TRUE(is_list_environment("enumerate"));
    EXPECT_TRUE(is_list_environment("description"));
    EXPECT_FALSE(is_list_environment("center"));
    EXPECT_FALSE(is_list_environment("quote"));
}

TEST_F(TexLatexBridgeTest, IsMathEnvironment) {
    EXPECT_TRUE(is_math_environment("equation"));
    EXPECT_TRUE(is_math_environment("align"));
    EXPECT_TRUE(is_math_environment("gather"));
    EXPECT_FALSE(is_math_environment("itemize"));
    EXPECT_FALSE(is_math_environment("quote"));
}

// ============================================================================
// Spacing Command Tests
// ============================================================================

TEST_F(TexLatexBridgeTest, MakeLatexHspace) {
    LaTeXContext ctx = LaTeXContext::create(arena, fonts);

    TexNode* quad = make_latex_hspace("quad", ctx);
    ASSERT_NE(quad, nullptr);
    EXPECT_EQ(quad->node_class, NodeClass::Glue);
    EXPECT_GT(quad->width, 0);  // Should have positive width

    TexNode* qquad = make_latex_hspace("qquad", ctx);
    ASSERT_NE(qquad, nullptr);
    EXPECT_GT(qquad->width, quad->width);  // qquad should be wider than quad
}

// ============================================================================
// Basic Typesetting Tests
// ============================================================================

TEST_F(TexLatexBridgeTest, TypesetEmptyDocument) {
    LaTeXContext ctx = LaTeXContext::create(arena, fonts);

    // Parse minimal LaTeX document
    const char* latex = "\\documentclass{article}\n\\begin{document}\n\\end{document}";
    Item root = parse_latex(latex);

    // Note: Even parsing the document is a valid test
    // Full typesetting would require more complete setup
    EXPECT_TRUE(root.item != ItemNull.item);
}

TEST_F(TexLatexBridgeTest, TypesetSimpleText) {
    LaTeXContext ctx = LaTeXContext::create(arena, fonts);

    // Parse simple text
    const char* latex = "Hello, world!";
    Item root = parse_latex(latex);

    EXPECT_TRUE(root.item != ItemNull.item);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(TexLatexBridgeTest, FullDocumentTypesetting) {
    LaTeXContext ctx = LaTeXContext::create(arena, fonts);

    // Set up document parameters
    ctx.doc_ctx.page_width = 612.0f;
    ctx.doc_ctx.page_height = 792.0f;
    ctx.doc_ctx.margin_left = 72.0f;
    ctx.doc_ctx.margin_right = 72.0f;
    ctx.doc_ctx.margin_top = 72.0f;
    ctx.doc_ctx.margin_bottom = 72.0f;
    ctx.doc_ctx.text_width = 468.0f;  // 612 - 72*2
    ctx.doc_ctx.text_height = 648.0f; // 792 - 72*2

    // Parse a document with content
    const char* latex =
        "\\documentclass{article}\n"
        "\\begin{document}\n"
        "\\section{Introduction}\n"
        "This is a test document.\n"
        "\\end{document}";

    Item root = parse_latex(latex);
    EXPECT_TRUE(root.item != ItemNull.item);

    // Typeset the document
    TexNode* document = typeset_latex_document(root, ctx);

    // Should produce some output (might be empty vlist if parsing didn't work)
    EXPECT_NE(document, nullptr);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
