// test_unified_dvi_gtest.cpp - Test unified pipeline DVI output
//
// Tests the doc_model_to_texnode -> DVI output path to verify the
// unified pipeline produces valid DVI output.

#include <gtest/gtest.h>
#include "lambda/tex/tex_document_model.hpp"
#include "lambda/tex/tex_latex_bridge.hpp"
#include "lambda/tex/tex_dvi_out.hpp"
#include "lambda/tex/tex_linebreak.hpp"
#include "lambda/tex/tex_pagebreak.hpp"
#include "lambda/tex/dvi_parser.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include <cstring>
#include <cstdio>

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class UnifiedDVITest : public ::testing::Test {
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
    
    // Helper to create document model from LaTeX
    TexDocumentModel* create_doc_model(const char* latex) {
        return doc_model_from_string(latex, strlen(latex), arena, fonts);
    }
    
    // Helper to create LaTeX context
    LaTeXContext create_context() {
        return LaTeXContext::create(arena, fonts);
    }
    
    // Helper to write DVI to file
    bool write_dvi(TexNode* content, const char* filename) {
        DVIWriter writer(arena);
        DVIParams params = DVIParams::defaults();
        
        if (!dvi_open(writer, filename, params)) {
            return false;
        }
        
        dvi_write_preamble(writer);
        
        // Begin single page
        dvi_begin_page(writer, 1);
        
        // Define and select a font
        uint32_t cmr10 = dvi_define_font(writer, "cmr10", 10.0f, 0);
        dvi_select_font(writer, cmr10);
        
        // Output the content
        if (content) {
            dvi_output_node(writer, content, fonts);
        }
        
        dvi_end_page(writer);
        dvi_write_postamble(writer);
        
        return dvi_close(writer);
    }
};

// ============================================================================
// Basic Tests
// ============================================================================

TEST_F(UnifiedDVITest, SimpleParagraph) {
    const char* latex = R"(
\documentclass{article}
\begin{document}
Hello world.
\end{document}
)";
    
    TexDocumentModel* doc = create_doc_model(latex);
    ASSERT_NE(doc, nullptr);
    ASSERT_NE(doc->root, nullptr);
    
    LaTeXContext ctx = create_context();
    
    // Convert to TexNode without typesetting
    TexNode* content = doc_model_to_texnode(doc, arena, ctx);
    ASSERT_NE(content, nullptr);
    
    // Check that content is a VList
    EXPECT_EQ(content->node_class, NodeClass::VList);
    
    // Write to DVI
    const char* dvi_path = "test_output/unified_simple.dvi";
    EXPECT_TRUE(write_dvi(content, dvi_path));
    
    // Verify DVI is readable
    Arena* parse_arena = arena_create_default(pool);
    
    dvi::DVIParser parser(parse_arena);
    EXPECT_TRUE(parser.parse_file(dvi_path));
    EXPECT_GE(parser.page_count(), 1);
    
    arena_destroy(parse_arena);
}

TEST_F(UnifiedDVITest, TypesetParagraph) {
    const char* latex = R"(
\documentclass{article}
\begin{document}
This is a longer paragraph that should be broken into multiple lines 
when the line breaking algorithm is applied. The text should flow 
naturally and produce proper typeset output.
\end{document}
)";
    
    TexDocumentModel* doc = create_doc_model(latex);
    ASSERT_NE(doc, nullptr);
    
    LaTeXContext ctx = create_context();
    
    // Use full typesetting with line breaking
    LineBreakParams line_params = LineBreakParams::defaults();
    line_params.hsize = 300.0f;  // Narrow line width for testing
    
    PageBreakParams page_params = PageBreakParams::defaults();
    page_params.page_height = 0;  // No page breaking
    
    TexNode* content = doc_model_typeset(doc, arena, ctx, line_params, page_params);
    ASSERT_NE(content, nullptr);
    
    // Write to DVI
    const char* dvi_path = "test_output/unified_typeset.dvi";
    EXPECT_TRUE(write_dvi(content, dvi_path));
    
    // Verify DVI is readable
    Arena* parse_arena = arena_create_default(pool);
    
    dvi::DVIParser parser(parse_arena);
    EXPECT_TRUE(parser.parse_file(dvi_path));
    EXPECT_GE(parser.page_count(), 1);
    
    arena_destroy(parse_arena);
}

TEST_F(UnifiedDVITest, SectionHeading) {
    const char* latex = R"(
\documentclass{article}
\begin{document}
\section{Introduction}
This is the introduction.
\end{document}
)";
    
    TexDocumentModel* doc = create_doc_model(latex);
    ASSERT_NE(doc, nullptr);
    
    LaTeXContext ctx = create_context();
    TexNode* content = doc_model_to_texnode(doc, arena, ctx);
    ASSERT_NE(content, nullptr);
    
    // Write to DVI
    const char* dvi_path = "test_output/unified_section.dvi";
    EXPECT_TRUE(write_dvi(content, dvi_path));
    
    // Verify DVI
    Arena* parse_arena = arena_create_default(pool);
    
    dvi::DVIParser parser(parse_arena);
    EXPECT_TRUE(parser.parse_file(dvi_path));
    
    arena_destroy(parse_arena);
}

TEST_F(UnifiedDVITest, ItemizeList) {
    const char* latex = R"(
\documentclass{article}
\begin{document}
\begin{itemize}
\item First item
\item Second item
\item Third item
\end{itemize}
\end{document}
)";
    
    TexDocumentModel* doc = create_doc_model(latex);
    ASSERT_NE(doc, nullptr);
    
    LaTeXContext ctx = create_context();
    TexNode* content = doc_model_to_texnode(doc, arena, ctx);
    ASSERT_NE(content, nullptr);
    
    // Write to DVI
    const char* dvi_path = "test_output/unified_itemize.dvi";
    EXPECT_TRUE(write_dvi(content, dvi_path));
}

TEST_F(UnifiedDVITest, InlineMath) {
    const char* latex = R"(
\documentclass{article}
\begin{document}
The equation $x^2 + y^2 = z^2$ is the Pythagorean theorem.
\end{document}
)";
    
    TexDocumentModel* doc = create_doc_model(latex);
    ASSERT_NE(doc, nullptr);
    
    LaTeXContext ctx = create_context();
    TexNode* content = doc_model_to_texnode(doc, arena, ctx);
    ASSERT_NE(content, nullptr);
    
    // Write to DVI
    const char* dvi_path = "test_output/unified_math.dvi";
    EXPECT_TRUE(write_dvi(content, dvi_path));
}

TEST_F(UnifiedDVITest, BoldItalic) {
    const char* latex = R"(
\documentclass{article}
\begin{document}
Normal text, \textbf{bold text}, and \textit{italic text}.
\end{document}
)";
    
    TexDocumentModel* doc = create_doc_model(latex);
    ASSERT_NE(doc, nullptr);
    
    LaTeXContext ctx = create_context();
    TexNode* content = doc_model_to_texnode(doc, arena, ctx);
    ASSERT_NE(content, nullptr);
    
    // Write to DVI
    const char* dvi_path = "test_output/unified_formatting.dvi";
    EXPECT_TRUE(write_dvi(content, dvi_path));
}

// ============================================================================
// Integration Test - Full Document
// ============================================================================

TEST_F(UnifiedDVITest, FullDocument) {
    const char* latex = R"(
\documentclass{article}
\title{Test Document}
\author{Test Author}
\begin{document}

\section{Introduction}
This is a test document for the unified LaTeX to DVI pipeline.
It contains various elements to verify the conversion works correctly.

\section{Lists}
Here is a list:
\begin{itemize}
\item First item with some text
\item Second item
\end{itemize}

\section{Math}
The quadratic formula is $x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}$.

\section{Conclusion}
The unified pipeline successfully converts LaTeX to DVI.

\end{document}
)";
    
    TexDocumentModel* doc = create_doc_model(latex);
    ASSERT_NE(doc, nullptr);
    
    LaTeXContext ctx = create_context();
    
    // Full typesetting
    LineBreakParams line_params = LineBreakParams::defaults();
    PageBreakParams page_params = PageBreakParams::defaults();
    page_params.page_height = 0;  // No page breaking for this test
    
    TexNode* content = doc_model_typeset(doc, arena, ctx, line_params, page_params);
    ASSERT_NE(content, nullptr);
    
    // Write to DVI
    const char* dvi_path = "test_output/unified_full_doc.dvi";
    EXPECT_TRUE(write_dvi(content, dvi_path));
    
    // Verify DVI
    Arena* parse_arena = arena_create_default(pool);
    
    dvi::DVIParser parser(parse_arena);
    EXPECT_TRUE(parser.parse_file(dvi_path));
    EXPECT_GE(parser.page_count(), 1);
    
    // Check that we got some content
    const dvi::DVIPage* page = parser.page(0);
    ASSERT_NE(page, nullptr);
    // The page should have some glyphs
    EXPECT_GT(page->glyph_count, 0);
    
    arena_destroy(parse_arena);
}

int main(int argc, char** argv) {
    // Create output directory
    system("mkdir -p test_output");
    
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
